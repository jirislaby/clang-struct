class ApplicationController < ActionController::Base
  helper ApplicationHelper

  def listing_limit
    return 500
  end
end
